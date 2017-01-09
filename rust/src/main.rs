extern crate hyper;
extern crate image;
extern crate rand;

use std::cmp;
use std::collections::LinkedList;
use std::env::args;
use std::sync::{Arc, Mutex};
use std::thread;

use hyper::server::{Request, Response};
use hyper::status::StatusCode;
use hyper::uri::RequestUri;
use image::{GenericImage, Pixel};

#[derive(Debug, Clone)]
struct Color {
    a: u8,
    r: u8,
    g: u8,
    b: u8,
}

#[derive(Debug, Clone)]
struct Circle {
    x: i64,
    y: i64,
    radius: i64,
    color: Color,
}

#[derive(Debug, Clone)]
struct Square(Circle);

trait Shape {
    fn new(x: i64, y: i64, radius: i64, color: Color) -> Self;
    fn x(&self) -> i64;
    fn y(&self) -> i64;
    fn radius(&self) -> i64;
    fn color(&self) -> Color;
    fn stride(&self, y: i64) -> i64;
}

impl Shape for Circle {
    fn new(x: i64, y: i64, radius: i64, color: Color) -> Self {
        Circle {
            x: x,
            y: y,
            radius: radius,
            color: color,
        }
    }

    fn x(&self) -> i64 {
        self.x
    }

    fn y(&self) -> i64 {
        self.y
    }

    fn radius(&self) -> i64 {
        self.radius
    }

    fn color(&self) -> Color {
        self.color.clone()
    }

    fn stride(&self, y: i64) -> i64 {
        let d = y - self.y;
        ((self.radius * self.radius) as f64 - (d * d) as f64).sqrt() as i64
    }
}

impl Shape for Square {
    fn new(x: i64, y: i64, radius: i64, color: Color) -> Self {
        Square(Circle {
            x: x,
            y: y,
            radius: radius,
            color: color,
        })
    }


    fn x(&self) -> i64 {
        self.0.x
    }

    fn y(&self) -> i64 {
        self.0.y
    }

    fn radius(&self) -> i64 {
        self.0.radius
    }

    fn color(&self) -> Color {
        self.0.color.clone()
    }

    fn stride(&self, _: i64) -> i64 {
        self.0.radius
    }
}

struct Pixels {
    w: u64,
    h: u64,
    pixels: Vec<u8>, // length=w*h*3
}

impl Pixels {
    fn create(w: u64, h: u64) -> Self {
        Pixels {
            w: w,
            h: h,
            pixels: vec![0; (w * h * 3) as usize],
        }
    }

    fn create_from(img: image::DynamicImage) -> Self {
        let (w, h) = (img.width(), img.height());
        let mut pixels: Vec<u8> = vec![0; (w * h * 3) as usize];
        let mut index: usize = 0;

        for y in 0..h {
            for x in 0..w {
                let rgb: [u8; 3] = img.get_pixel(x, y).to_rgb().data;
                pixels[index] = rgb[0];
                for i in 0..3 {
                    pixels[index + i] = rgb[i];
                }
                index += 3;
            }
        }
        Pixels {
            w: w as u64,
            h: h as u64,
            pixels: pixels,
        }
    }
}

fn random(n: u64) -> u64 {
    rand::random::<u64>() % n
}

fn distance(img1: &Pixels, img2: &Pixels) -> u64 {
    let (w, h) = (img1.w, img1.h);
    let l = (w * h * 3) as usize;
    let mut sum: u64 = 0;

    assert!(w == img2.w && h == img2.h);
    for i in 0..l {
        let d = img1.pixels[i] as i64 - img2.pixels[i] as i64;
        sum += (d * d) as u64;
    }
    sum
}

fn mutate<T: Shape>(mut shapes: LinkedList<T>, w: u64, h: u64) -> LinkedList<T> {
    if random(2) == 0 {
        let color: Color = Color {
            a: (random(100) + 20) as u8,
            r: random(256) as u8,
            g: random(256) as u8,
            b: random(256) as u8,
        };
        let new_shape: T = Shape::new(random(w) as i64,
                                      random(h) as i64,
                                      random(50) as i64 + 1,
                                      color);

        shapes.push_front(new_shape);
    } else if !shapes.is_empty() {
        let index = random(shapes.len() as u64);
        let mut tail: LinkedList<T> = shapes.split_off(index as usize);

        tail.pop_front();
        shapes.append(&mut tail);
    }
    if random(2) == 0 {
        shapes = mutate(shapes, w, h);
    }
    shapes
}

fn draw_to_pixels<T: Shape>(shapes: &LinkedList<T>, w: u64, h: u64) -> Pixels {
    let mut img: Pixels = Pixels::create(w, h);
    let mut iter = shapes.iter();

    while let Some(ref shape) = iter.next() {
        let pixels: &mut Vec<u8> = &mut img.pixels;
        let a_inv = 255 - (shape.color().a as u64);
        let r_blend = (shape.color().r as u64) * (shape.color().a as u64) >> 8;
        let g_blend = (shape.color().g as u64) * (shape.color().a as u64) >> 8;
        let b_blend = (shape.color().b as u64) * (shape.color().a as u64) >> 8;
        let from = cmp::max(0, shape.y() - shape.radius());
        let to = cmp::min(shape.y() + shape.radius() + 1, h as i64);

        for y in from..to {
            let stride = shape.stride(y);
            let from_x = cmp::max(0, shape.x() - stride);
            let to_x = cmp::min(shape.x() + stride + 1, w as i64);

            for x in from_x..to_x {
                let index = ((y * w as i64 + x) * 3) as usize;
                let r = pixels[index + 0] as u64;
                let g = pixels[index + 1] as u64;
                let b = pixels[index + 2] as u64;

                pixels[index + 0] = ((a_inv * r >> 8) + r_blend) as u8;
                pixels[index + 1] = ((a_inv * g >> 8) + g_blend) as u8;
                pixels[index + 2] = ((a_inv * b >> 8) + b_blend) as u8;
            }
        }
    }
    img
}

fn draw_to_img(pixels: Pixels) -> image::DynamicImage {
    let mut imgbuf = image::ImageBuffer::new(pixels.w as u32, pixels.h as u32);
    for (x, y, pixel) in imgbuf.enumerate_pixels_mut() {
        let index = ((y * pixels.w as u32 + x) * 3) as usize;
        let r = pixels.pixels[index + 0];
        let g = pixels.pixels[index + 1];
        let b = pixels.pixels[index + 2];

        *pixel = image::Rgb([r, g, b])
    }
    image::ImageRgb8(imgbuf)
}

struct Lisa<T: Shape> {
    w: u64,
    h: u64,
    round: u64,
    score: u64,
    shapes: LinkedList<T>,
}

fn run<T: Shape + Clone + Send + 'static>() {
    let img = image::open("../pics/lisa.jpg").unwrap();
    let w: u64 = img.dimensions().0 as u64;
    let h: u64 = img.dimensions().1 as u64;

    let lisa_img = Pixels::create_from(img);
    let population: usize = 100;
    let mut shapes_vec: Vec<LinkedList<T>> = vec![LinkedList::new(); population];

    let lisa_ctx: Arc<Mutex<Lisa<T>>> = Arc::new(Mutex::new(Lisa {
        w: w,
        h: h,
        round: 0,
        score: std::u64::MAX,
        shapes: shapes_vec[0].clone(),
    }));

    {
        let lisa_ctx = lisa_ctx.clone();  // increase the reference count.
        thread::spawn(move || {
            let http_server: hyper::Server = hyper::Server::http("localhost:8080").unwrap();

            http_server.handle(move |req: Request, mut res: Response| {
                if let (hyper::Get, RequestUri::AbsolutePath(ref uri)) = (req.method, req.uri) {
                    match uri.as_str() {
                        "/lisa.png" => {
                            let lisa_ctx = lisa_ctx.lock().unwrap();
                            let mut buf: Vec<u8> = Vec::new();

                            draw_to_img(draw_to_pixels(&lisa_ctx.shapes,
                                                       lisa_ctx.w,
                                                       lisa_ctx.h)).save(&mut buf, image::PNG);
                            res.headers_mut().set_raw("Content-Type", vec![b"image/png".to_vec()]);
                            *res.status_mut() = StatusCode::Ok;
                            res.send(&buf);
                            return;
                        }
                        "/lisa" => {
                            let lisa_ctx = lisa_ctx.lock().unwrap();
                            let buf: String = format!("<html> \
                                                         Round: {}<br/> \
                                                         Score: {}<br/> \
                                                         <img src=\"lisa.png\"> \
                                                       </html>", lisa_ctx.round, lisa_ctx.score);

                            *res.status_mut() = StatusCode::Ok;
                            res.send(buf.as_bytes());
                            return;
                        }
                        _ => *res.status_mut() = StatusCode::NotFound,
                    }
                }
                *res.status_mut() = StatusCode::NotFound;
            }).unwrap();
        });
    }

    loop {
        shapes_vec = shapes_vec.into_iter().map(|shapes| mutate(shapes, w, h)).collect();
        {
            let win: LinkedList<T>;
            {
                let mut best: &LinkedList<T> = &shapes_vec[0];
                let mut best_score: u64 = distance(&lisa_img, &draw_to_pixels(best, w, h));

                for i in 1..population {
                    let img: Pixels = draw_to_pixels(&shapes_vec[i], w, h);
                    let score: u64 = distance(&lisa_img, &img);

                    if score < best_score {
                        best_score = score;
                        best = &shapes_vec[i];
                    }
                }

                let mut lisa_ctx = lisa_ctx.lock().unwrap();
                if best_score < lisa_ctx.score {
                    lisa_ctx.score = best_score;
                    lisa_ctx.shapes = best.clone();
                }
                lisa_ctx.round += 1;
                win = lisa_ctx.shapes.clone();

                if lisa_ctx.round % 30 == 0 {
                    println!("round = {}, score = {}, shapes = {}.",
                             lisa_ctx.round, lisa_ctx.score, lisa_ctx.shapes.len());
                }
            }

            for shape in &mut shapes_vec {
                *shape = win.clone();
            }
        }
    }
}

fn main() {
    if args().len() == 2 && args().collect::<Vec<String>>()[1] == "-square" {
        run::<Square>();
    } else {
        run::<Circle>();
    }
}
