package main

import (
	"image"
	"image/color"
	"image/draw"
	"image/jpeg"
	"image/png"
	"log"
	"math"
	"math/rand"
	"os"
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

	for y, i := 0, 0; y < h; y++ {
		for x := 0; x < w; x++ {
			for p := 0; p < 3; p++ {
				c := uint64(lhs.Pixels[i]) - uint64(rhs.Pixels[i])
				i += 1
				ret += c * c
			}
		}
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
	var lisaFile *os.File
	var lisaImg image.Image

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
	oldBestScore := uint64(math.MaxUint64)

	for it := 0; ; it++ {
		bestScore, bestIndex, bestCircles := uint64(0), -1, clone(circles[0])

		for i := 0; i < Population; i++ {
			circles[i] = mutateCircles(circles[i], w, h)
			d := distance(lisaPixels, drawToPixel(circles[i], w, h))
			if bestIndex == -1 || d < bestScore {
				bestScore, bestIndex = d, i
			}
		}

		if bestScore < oldBestScore {
			oldBestScore, bestCircles = bestScore, clone(circles[bestIndex])
		}

		for i := 0; i < Population; i++ {
			circles[i] = clone(bestCircles)
		}

		if it%10 == 0 {
			newPixels := drawToPixel(bestCircles, w, h)
			log.Printf("%d %d %d\n", it, len(bestCircles), distance(lisaPixels, newPixels))
			newImg := drawToPNG(newPixels)
			newFile, _ := os.Create("/tmp/lisa.png")
			png.Encode(newFile, newImg)
			newFile.Close()
		}
	}
}
