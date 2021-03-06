package main

import (
	"flag"
	"fmt"
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"image/png"
	"log"
	"math"
	"math/rand"
	"net/http"
	"os"
	"sync"
	"time"
)

type Pixels struct {
	Pixels []byte // length = 3*w*h
	Width  int
	Height int
}

type Circle struct {
	X, Y       int
	Radius     int
	A, R, G, B uint8
}

func createPixels(w int, h int, img image.Image) Pixels {
	var ret Pixels

	ret.Width = w
	ret.Height = h

	ret.Pixels = make([]byte, w*h*3)
	for y, i := 0, 0; y < h; y++ {
		if img != nil {
			for x := 0; x < w; x++ {
				r, g, b, _ := img.At(x+img.Bounds().Min.X, y+img.Bounds().Min.Y).RGBA()
				ret.Pixels[i+0] = byte(r >> 8)
				ret.Pixels[i+1] = byte(g >> 8)
				ret.Pixels[i+2] = byte(b >> 8)
				i += 3
			}
		}
	}

	return ret
}

func distance(lhs Pixels, rhs Pixels) (ret uint64) {
	w, h := lhs.Width, lhs.Height

	if rhs.Width != w || rhs.Height != h {
		log.Fatalf("image size mismatch")
	}

	for i := 0; i < w*h*3; i++ {
		c := uint64(lhs.Pixels[i]) - uint64(rhs.Pixels[i])
		ret += c * c
	}
	return
}

func mutateCircles(circles []Circle, w int, h int) []Circle {
	if rand.Intn(2) == 0 {
		var circle Circle

		circle.X = rand.Intn(w)
		circle.Y = rand.Intn(h)
		circle.Radius = rand.Intn(50) + 1
		circle.A = uint8(rand.Intn(100)) + 20
		circle.R = uint8(rand.Intn(256))
		circle.G = uint8(rand.Intn(256))
		circle.B = uint8(rand.Intn(256))

		circles = append(circles, circle)
	} else {
		if len(circles) != 0 {
			index := rand.Intn(len(circles))
			circles = append(circles[:index], circles[index+1:]...)
		}
	}

	if rand.Intn(2) == 0 {
		return mutateCircles(circles, w, h)
	}
	return circles
}

func drawToPixel(circles []Circle, w int, h int) Pixels {
	ret := createPixels(w, h, nil)

	for _, circle := range circles {
		radius2 := float64(circle.Radius * circle.Radius)
		a := uint(circle.A)
		aInv := 0xff - a
		rBlend := (uint(circle.R) * a) >> 8
		gBlend := (uint(circle.G) * a) >> 8
		bBlend := (uint(circle.B) * a) >> 8

		from, to := circle.Y-circle.Radius, circle.Y+circle.Radius
		if from < 0 {
			from = 0
		}
		if to >= h {
			to = h - 1
		}

		for y := from; y <= to; y++ {
			stride := int(math.Sqrt(radius2 - float64((y-circle.Y)*(y-circle.Y))))

			fromX, toX := circle.X-stride, circle.X+stride
			if fromX < 0 {
				fromX = 0
			}
			if toX >= w {
				toX = w - 1
			}
			for x := fromX; x <= toX; x++ {
				index := (y*w + x) * 3
				r := uint(ret.Pixels[index+0])
				g := uint(ret.Pixels[index+1])
				b := uint(ret.Pixels[index+2])

				ret.Pixels[index+0] = byte((aInv * r >> 8) + rBlend)
				ret.Pixels[index+1] = byte((aInv * g >> 8) + gBlend)
				ret.Pixels[index+2] = byte((aInv * b >> 8) + bBlend)
			}
		}
	}
	return ret
}

func drawToPNG(pixels Pixels) image.Image {
	w, h := pixels.Width, pixels.Height
	img := draw.Image(image.NewRGBA(image.Rect(0, 0, w, h)))

	for y, i := 0, 0; y < h; y++ {
		for x := 0; x < w; x++ {
			img.Set(x, y, color.RGBA{pixels.Pixels[i+0], pixels.Pixels[i+1], pixels.Pixels[i+2], 0xff})
			i += 3
		}
	}
	return image.Image(img)
}

func main() {
	var port = flag.Int("port", 8080, "port")
	flag.Parse()

	var lisaFile *os.File
	var lisaImg image.Image

	startTime := time.Now()

	lisaFile, err := os.Open("../pics/lisa.jpg")
	if err != nil {
		log.Fatal(err)
	}
	lisaImg, err = jpeg.Decode(lisaFile)
	if err != nil {
		log.Fatal(err)
	}

	w := lisaImg.Bounds().Max.X - lisaImg.Bounds().Min.X
	h := lisaImg.Bounds().Max.Y - lisaImg.Bounds().Min.Y
	lisaPixels := createPixels(w, h, lisaImg)

	const Population = 100

	clone :=
		func(circles []Circle) (ret []Circle) {
			ret = make([]Circle, len(circles))
			copy(ret, circles)
			return ret
		}

	circles := make([][]Circle, Population)
	bestEver, bestCircles, round := uint64(math.MaxUint64), clone(circles[0]), 0
	var mux sync.Mutex // mutex for the previous 3 variables

	go func(width int, height int) {
		http.HandleFunc("/lisa", func(w http.ResponseWriter, r *http.Request) {
			mux.Lock()
			defer mux.Unlock()

			fmt.Fprintf(w, "<html>Round: %v <br/>Score: %v <br/>Elapsed: %v <br/><img src=\"lisa.png\"></html>", round, bestEver, time.Since(startTime))
		})
		http.HandleFunc("/lisa.png", func(w http.ResponseWriter, r *http.Request) {
			w.Header().Set("Content-Type", "image/png")

			mux.Lock()
			defer mux.Unlock()

			img := drawToPNG(drawToPixel(bestCircles, width, height))
			png.Encode(w, img)
		})
		http.ListenAndServe(fmt.Sprintf("localhost:%d", *port), nil)
	}(w, h)

	fmt.Printf("go http://localhost:%d/lisa for funny stuffs.\n", *port)

	for it := 0; ; it++ {
		bestScore, bestIndex := uint64(0), -1

		for i := 0; i < Population; i++ {
			circles[i] = mutateCircles(circles[i], w, h)
			d := distance(lisaPixels, drawToPixel(circles[i], w, h))
			if bestIndex == -1 || d < bestScore {
				bestScore, bestIndex = d, i
			}
		}

		mux.Lock()
		if bestScore < bestEver {
			bestEver, bestCircles = bestScore, clone(circles[bestIndex])
		}

		for i := 0; i < Population; i++ {
			circles[i] = clone(bestCircles)
		}
		round = it
		mux.Unlock()
	}
}
