//! MuPDF document core for the native FLTK viewer. Handles stay on their owning
//! render worker; neither MuPDF objects nor borrowed output buffers cross threads.
pub mod bookmarks;
pub mod config;
pub mod geometry;
pub mod jumplist;
pub mod pdf;

use std::{
    cell::RefCell,
    collections::HashMap,
    ffi::{CStr, CString, c_char},
    io::Read,
    path::PathBuf,
    ptr,
    sync::atomic::{AtomicBool, AtomicU64, Ordering},
};
use anyhow::{Result, Context, bail, ensure};
use mupdf::{
    Document, Page, DisplayList, Colorspace, Device, Matrix, Pixmap, Rect, TextPageFlags,
    MetadataName,
};
use serde_json::{Value, json};
use crate::geometry::{Rect as GeoRect, Vector};

static CANCEL_REVISION: AtomicU64 = AtomicU64::new(0);
static SEARCH_CANCELLED: AtomicBool = AtomicBool::new(false);
#[unsafe(no_mangle)]
pub extern "C" fn miro_cancel_pending() {
    CANCEL_REVISION.fetch_add(1, Ordering::Relaxed);
    SEARCH_CANCELLED.store(true, Ordering::Relaxed);
}
thread_local! {static OUTPUT:RefCell<CString>=RefCell::new(CString::new("{}").unwrap());}
fn output(value: Value) -> *const c_char {
    OUTPUT.with(|slot| {
        *slot.borrow_mut() = CString::new(value.to_string())
            .unwrap_or_else(|_| CString::new("{\"error\":\"Invalid output\"}").unwrap());
        slot.borrow().as_ptr()
    })
}
fn response(f: impl FnOnce() -> Result<Value>) -> *const c_char {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(Ok(v)) => output(v),
        Ok(Err(e)) => output(json!({"error":format!("{e:#}")})),
        Err(_) => output(json!({"error":"Document operation failed"})),
    }
}
unsafe fn input(pointer: *const c_char) -> Result<Value> {
    ensure!(!pointer.is_null(), "Missing request");
    Ok(serde_json::from_slice(
        unsafe { CStr::from_ptr(pointer) }.to_bytes(),
    )?)
}
fn array_rect(r: Rect) -> [f32; 4] {
    [r.x0, r.y0, r.x1, r.y1]
}
fn geo_rect(r: Rect) -> GeoRect<f32> {
    GeoRect::from_pos_size(
        Vector::new(r.x0, r.y0),
        Vector::new(r.x1 - r.x0, r.y1 - r.y0),
    )
}

#[derive(Debug)]
pub struct DocumentHandle {
    doc: Document,
    source: String,
    bytes: Option<Vec<u8>>,
    bounds: Vec<Rect>,
    display_lists: HashMap<usize, DisplayList>,
    pixels: Vec<u8>,
}
impl DocumentHandle {
    pub fn open(source: &str, password: &str) -> Result<Self> {
        let (mut doc, bytes) = if source.starts_with("http://") || source.starts_with("https://") {
            let client = reqwest::blocking::Client::builder()
                .timeout(std::time::Duration::from_secs(30))
                .build()?;
            let response = client.get(source).send()?.error_for_status()?;
            let mut bytes = Vec::new();
            response
                .take(512 * 1024 * 1024 + 1)
                .read_to_end(&mut bytes)?;
            ensure!(
                bytes.len() <= 512 * 1024 * 1024,
                "PDF download exceeds 512 MiB"
            );
            (
                Document::from_bytes(&bytes, "application/pdf")?,
                Some(bytes),
            )
        } else {
            (Document::open(source)?, None)
        };
        if doc.needs_password()? {
            ensure!(!password.is_empty(), "Password required");
            ensure!(doc.authenticate(password)?, "Incorrect password");
        }
        ensure!(doc.is_pdf(), "This document is not a PDF");
        let count = doc.page_count()?;
        ensure!(count > 0 && count <= 100_000, "Invalid PDF page count");
        let mut bounds = Vec::with_capacity(count as usize);
        for n in 0..count {
            let rect = doc.load_page(n)?.bounds()?;
            ensure!(
                [rect.x0, rect.y0, rect.x1, rect.y1]
                    .iter()
                    .all(|v| v.is_finite() && v.abs() < 1e8)
                    && rect.x1 > rect.x0
                    && rect.y1 > rect.y0,
                "Invalid PDF page bounds"
            );
            bounds.push(rect);
        }
        Ok(Self {
            doc,
            source: source.into(),
            bytes,
            bounds,
            display_lists: HashMap::new(),
            pixels: Vec::new(),
        })
    }
    fn page(&self, n: usize) -> Result<Page> {
        ensure!(n < self.bounds.len(), "Page index is out of range");
        Ok(self.doc.load_page(n as i32)?)
    }
    fn display_list(&mut self, n: usize) -> Result<&DisplayList> {
        if !self.display_lists.contains_key(&n) {
            if self.display_lists.len() >= 12 {
                self.display_lists.clear();
            }
            self.display_lists
                .insert(n, self.page(n)?.to_display_list(true)?);
        }
        Ok(&self.display_lists[&n])
    }
    pub fn metadata(&self) -> Result<Value> {
        fn outline(items: Vec<mupdf::Outline>) -> Vec<Value> {
            items.into_iter().map(|v|json!({"title":v.title,"uri":v.uri,"page":v.dest.map(|d|d.loc.page_number),"children":outline(v.down)})).collect()
        }
        Ok(
            json!({"source":self.source,"page_count":self.bounds.len(),"bounds":self.bounds.iter().copied().map(array_rect).collect::<Vec<_>>(),
            "title":self.doc.metadata(MetadataName::Title).unwrap_or_default(),"author":self.doc.metadata(MetadataName::Author).unwrap_or_default(),
            "outline":outline(self.doc.outlines().unwrap_or_default())}),
        )
    }
    fn chars(&self, n: usize) -> Result<(String, Vec<(usize, usize, GeoRect<f32>)>)> {
        let text = self.page(n)?.to_text_page(TextPageFlags::empty())?;
        let mut out = String::new();
        let mut boxes = Vec::new();
        for block in text.blocks() {
            for line in block.lines() {
                for c in line.chars() {
                    if let Some(ch) = c.char() {
                        boxes.push((n, out.len(), geo_rect(c.quad().into())));
                        out.push(ch);
                    }
                }
                out.push('\n');
            }
        }
        Ok((out, boxes))
    }
    fn page_data(&self, n: usize) -> Result<Value> {
        let page = self.page(n)?;
        let text = page.to_text_page(TextPageFlags::empty())?;
        let words=text.words().into_iter().map(|w|json!({"text":w.text,"rect":array_rect(w.bounds),"block":w.block,"line":w.line})).collect::<Vec<_>>();
        let links=page.links()?.map(|l|json!({"rect":array_rect(l.bounds),"uri":l.uri,"page":l.dest.map(|d|d.loc.page_number)})).collect::<Vec<_>>();
        let mut comments = Vec::new();
        if let Ok(page) = mupdf::pdf::PdfPage::try_from(page) {
            for ann in page.annotations() {
                let kind = ann
                    .r#type()
                    .ok()
                    .map(|v| format!("{v:?}"))
                    .unwrap_or_default();
                if kind == "Popup" {
                    continue;
                }
                let content = ann
                    .contents()
                    .ok()
                    .flatten()
                    .map(|v| v.to_string())
                    .unwrap_or_default();
                let author = ann
                    .author()
                    .ok()
                    .flatten()
                    .map(|v| v.to_string())
                    .unwrap_or_default();
                if !content.is_empty() {
                    comments.push(json!({"text":content,"author":author,"type":kind,"rect":ann.rect().ok().map(array_rect)}));
                }
            }
        }
        Ok(json!({"page":n,"text":text.to_text()?,"words":words,"links":links,"comments":comments}))
    }
    fn search(&self, needle: &str, regex: bool) -> Result<Value> {
        ensure!(needle.len() <= 8192, "Search expression is too long");
        if regex {
            regex::Regex::new(needle).context("Invalid search expression")?;
        }
        SEARCH_CANCELLED.store(false, Ordering::Relaxed);
        let revision = CANCEL_REVISION.load(Ordering::Relaxed);
        let mut text = String::new();
        let mut boxes = Vec::new();
        for n in 0..self.bounds.len() {
            ensure!(
                CANCEL_REVISION.load(Ordering::Relaxed) == revision,
                "Search cancelled"
            );
            let (s, mut b) = self.chars(n)?;
            for (_, offset, _) in &mut b {
                *offset += text.len();
            }
            text.push_str(&s);
            boxes.extend(b);
            ensure!(
                text.len() <= 64 * 1024 * 1024 && boxes.len() <= 4_000_000,
                "Document text exceeds search memory budget"
            );
        }
        ensure!(
            CANCEL_REVISION.load(Ordering::Relaxed) == revision,
            "Search cancelled"
        );
        let method = if regex {
            pdf::SearchMethod::Regex
        } else {
            pdf::SearchMethod::PlainText
        };
        let matches = pdf::find_search_matches(&text, needle, method, &boxes, &SEARCH_CANCELLED);
        Ok(
            json!({"matches":matches.into_iter().take(10000).map(|m|json!({"page":m.pages.start,"rects":m.rects.into_iter().map(|(p,r)|json!({"page":p,"rect":[r.x0.x,r.x0.y,r.x1.x,r.x1.y]})).collect::<Vec<_>>(),"snippet":text[m.start_byte..m.end_byte].chars().take(100).collect::<String>()})).collect::<Vec<_>>()}),
        )
    }
    fn request(&mut self, input: &Value) -> Result<Value> {
        match input["op"].as_str().unwrap_or("") {
            "metadata" => self.metadata(),
            "page" => self.page_data(input["page"].as_u64().unwrap_or(0) as usize),
            "search" => self.search(
                input["needle"].as_str().unwrap_or(""),
                input["regex"].as_bool().unwrap_or(false),
            ),
            "save_copy" => {
                let dest = PathBuf::from(input["path"].as_str().context("Missing output path")?);
                if let Some(bytes) = &self.bytes {
                    std::fs::write(dest, bytes)?;
                } else {
                    ensure!(
                        dest.canonicalize().ok() != PathBuf::from(&self.source).canonicalize().ok(),
                        "Choose a different output file"
                    );
                    std::fs::copy(&self.source, dest)?;
                }
                Ok(json!({}))
            }
            _ => bail!("Unknown document operation"),
        }
    }
    pub fn render(&mut self, input: &Value) -> Result<Value> {
        let n = input["page"].as_u64().context("Missing page")? as usize;
        let a = input["matrix"]
            .as_array()
            .context("Missing page transform")?;
        ensure!(a.len() == 6, "Invalid transform");
        let mut v = [0f32; 6];
        for i in 0..6 {
            v[i] = a[i].as_f64().context("Invalid transform")? as f32;
            ensure!(v[i].is_finite() && v[i].abs() <= 1.0e8, "Invalid transform");
        }
        let clip = input["clip"].as_array().context("Missing tile rectangle")?;
        ensure!(clip.len() == 4, "Invalid tile");
        let mut r = [0f32; 4];
        for i in 0..4 {
            r[i] = clip[i].as_f64().context("Invalid tile")? as f32;
            ensure!(r[i].is_finite() && r[i].abs() < 1.0e8, "Invalid tile");
        }
        let rect = Rect::new(r[0], r[1], r[2], r[3]);
        let w = (rect.x1.ceil() - rect.x0.floor()) as usize;
        let h = (rect.y1.ceil() - rect.y0.floor()) as usize;
        ensure!(
            w > 0 && h > 0 && w <= 16384 && h <= 16384 && w * h <= 24 * 1024 * 1024,
            "Page tile exceeds rendering budget"
        );
        let matrix = Matrix::new(v[0], v[1], v[2], v[3], v[4], v[5]);
        let list = self.display_list(n)?;
        let mut pixmap = Pixmap::new_with_rect(&Colorspace::device_rgb(), rect.into(), false)?;
        pixmap.clear_with(255)?;
        {
            let device = Device::from_pixmap(&pixmap)?;
            list.run(&device, &matrix, rect)?;
        }
        if input["dark"].as_bool().unwrap_or(false) {
            pixmap.invert()?;
        }
        let result = json!({"width":pixmap.width(),"height":pixmap.height(),"stride":pixmap.stride(),"components":pixmap.n(),"x":pixmap.x(),"y":pixmap.y()});
        self.pixels = pixmap.samples().to_vec();
        Ok(result)
    }
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_open(
    path: *const c_char,
    password: *const c_char,
) -> *mut DocumentHandle {
    let result = std::panic::catch_unwind(|| -> Result<DocumentHandle> {
        ensure!(!path.is_null(), "Missing PDF path");
        let path = unsafe { CStr::from_ptr(path) }.to_str()?;
        let password = if password.is_null() {
            ""
        } else {
            unsafe { CStr::from_ptr(password) }.to_str()?
        };
        DocumentHandle::open(path, password)
    });
    match result {
        Ok(Ok(v)) => Box::into_raw(Box::new(v)),
        Ok(Err(e)) => {
            output(json!({"error":format!("{e:#}")}));
            ptr::null_mut()
        }
        Err(_) => {
            output(json!({"error":"Cannot open document"}));
            ptr::null_mut()
        }
    }
}
#[unsafe(no_mangle)]
pub extern "C" fn miro_last_result() -> *const c_char {
    OUTPUT.with(|v| v.borrow().as_ptr())
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_close(handle: *mut DocumentHandle) {
    if !handle.is_null() {
        drop(unsafe { Box::from_raw(handle) });
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_request(
    handle: *mut DocumentHandle,
    request: *const c_char,
) -> *const c_char {
    response(|| {
        ensure!(!handle.is_null(), "No document");
        unsafe { &mut *handle }.request(&unsafe { input(request) }?)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_render(
    handle: *mut DocumentHandle,
    request: *const c_char,
) -> *const c_char {
    response(|| {
        ensure!(!handle.is_null(), "No document");
        unsafe { &mut *handle }.render(&unsafe { input(request) }?)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_pixels(handle: *mut DocumentHandle) -> *const u8 {
    if handle.is_null() {
        ptr::null()
    } else {
        unsafe { (*handle).pixels.as_ptr() }
    }
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_config(path: *const c_char) -> *const c_char {
    response(|| {
        let path = if path.is_null() {
            None
        } else {
            Some(PathBuf::from(unsafe { CStr::from_ptr(path) }.to_str()?))
        };
        Ok(serde_json::to_value(config::Config::load(path))?)
    })
}
#[unsafe(no_mangle)]
pub unsafe extern "C" fn miro_state(request: *const c_char) -> *const c_char {
    response(|| bookmarks::request(&unsafe { input(request) }?))
}

#[cfg(test)]
mod tests {
    use super::*;
    fn sample(name: &str) -> String {
        format!("{}/assets/{name}", env!("CARGO_MANIFEST_DIR"))
    }
    #[test]
    fn native_document_render_text_links_and_annotations() {
        let mut d = DocumentHandle::open(&sample("text-copy-test.pdf"), "").unwrap();
        assert!(!d.bounds.is_empty());
        let p = d.page_data(0).unwrap();
        assert!(!p["text"].as_str().unwrap().trim().is_empty());
        assert!(!p["words"].as_array().unwrap().is_empty());
        let r = d
            .render(&json!({"page":0,"matrix":[1,0,0,1,0,0],"clip":[0,0,96,96]}))
            .unwrap();
        assert_eq!(r["width"], 96);
        assert_eq!(d.pixels.len(), 96 * 96 * 3);
        let mut coordinates = DocumentHandle::open(&sample("coordinate_points.pdf"), "").unwrap();
        coordinates
            .render(&json!({"page":0,"matrix":[0.5,0,0,0.5,0,0],"clip":[0,0,298,421]}))
            .unwrap();
        assert!(
            coordinates.pixels.iter().filter(|p| **p < 200).count() > 100,
            "PDF drawing must contain actual page content"
        );
        let links = DocumentHandle::open(&sample("links.pdf"), "").unwrap();
        assert!(
            !links.page_data(0).unwrap()["links"]
                .as_array()
                .unwrap()
                .is_empty()
        );
        let ann = DocumentHandle::open(&sample("links_annotated.pdf"), "").unwrap();
        assert!(
            !ann.page_data(0).unwrap()["comments"]
                .as_array()
                .unwrap()
                .is_empty()
        );
    }
    #[test]
    fn encrypted_pdf_password_retry_preserves_document_content() {
        use mupdf::pdf::{PdfDocument, PdfWriteOptions, Encryption};
        let path = std::env::temp_dir().join(format!("miro-password-{}.pdf", std::process::id()));
        let pdf = PdfDocument::open(&sample("coordinate_points.pdf")).unwrap();
        let mut options = PdfWriteOptions::default();
        options
            .set_encryption(Encryption::Aes256)
            .set_owner_password("miro-owner-test")
            .set_user_password("miro-reader-test");
        pdf.save_with_options(path.to_str().unwrap(), options)
            .unwrap();
        assert!(
            DocumentHandle::open(path.to_str().unwrap(), "")
                .unwrap_err()
                .to_string()
                .contains("Password required")
        );
        assert!(
            DocumentHandle::open(path.to_str().unwrap(), "wrong")
                .unwrap_err()
                .to_string()
                .contains("Incorrect password")
        );
        let document = DocumentHandle::open(path.to_str().unwrap(), "miro-reader-test").unwrap();
        assert_eq!(document.bounds.len(), 2);
        assert!(
            document.page_data(0).unwrap()["text"]
                .as_str()
                .unwrap()
                .contains("297")
        );
        std::fs::remove_file(path).unwrap();
    }
    #[test]
    fn bad_documents_pages_and_unbounded_tiles_fail() {
        assert!(DocumentHandle::open("/does/not/exist.pdf", "").is_err());
        let mut d = DocumentHandle::open(&sample("text-copy-test.pdf"), "").unwrap();
        assert!(d.page_data(usize::MAX).is_err());
        assert!(
            d.render(&json!({"page":0,"matrix":[1,0,0,1,0,0],"clip":[0,0,20000,20000]}))
                .is_err()
        );
        assert!(d.search("(", true).is_err());
    }
    #[test]
    fn render_tile_matches_same_area_of_full_page() {
        let mut d = DocumentHandle::open(&sample("text-copy-test.pdf"), "").unwrap();
        let args = json!({"page":0,"matrix":[1,0,0,1,0,0],"clip":[0,0,128,128]});
        d.render(&args).unwrap();
        let full = d.pixels.clone();
        d.render(&json!({"page":0,"matrix":[1,0,0,1,0,0],"clip":[32,32,64,64]}))
            .unwrap();
        for y in 0..32 {
            assert_eq!(
                &d.pixels[y * 32 * 3..(y + 1) * 32 * 3],
                &full[((y + 32) * 128 + 32) * 3..((y + 32) * 128 + 64) * 3]
            );
        }
    }
}
