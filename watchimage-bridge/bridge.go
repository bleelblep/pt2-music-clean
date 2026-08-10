package watchimagebridge

import (
	"encoding/base64"
	"encoding/json"
	"fmt"

	"github.com/killdano/mirrormsg/watchimage"
)

func decodeToGColor8(result watchimage.WatchImageResult) ([]byte, int, int, error) {
	if result.Format == 0 {
		pixels, err := base64.StdEncoding.DecodeString(result.Pixels)
		if err != nil {
			return nil, 0, 0, err
		}
		return pixels, result.Width, result.Height, nil
	}

	packed, err := base64.StdEncoding.DecodeString(result.Pixels)
	if err != nil {
		return nil, 0, 0, err
	}
	palette, err := base64.StdEncoding.DecodeString(result.Palette)
	if err != nil {
		return nil, 0, 0, err
	}

	depth := result.Depth
	if depth != 1 && depth != 2 && depth != 4 {
		return nil, 0, 0, fmt.Errorf("unsupported depth %d", depth)
	}
	perByte := 8 / depth
	tightRow := (result.Width + perByte - 1) / perByte
	rowBytes := (tightRow + 3) &^ 3

	out := make([]byte, result.Width*result.Height)
	mask := byte((1 << depth) - 1)
	for y := 0; y < result.Height; y++ {
		rowStart := y * rowBytes
		for x := 0; x < result.Width; x++ {
			b := packed[rowStart+(x/perByte)]
			slot := x % perByte
			shift := 8 - depth*(slot+1)
			idx := int((b >> uint(shift)) & mask)
			if idx < 0 || idx >= len(palette) {
				idx = 0
			}
			out[y*result.Width+x] = palette[idx]
		}
	}
	return out, result.Width, result.Height, nil
}

func resizeNearest(src []byte, sw int, sh int, dw int, dh int) []byte {
	if sw == dw && sh == dh {
		return src
	}
	out := make([]byte, dw*dh)
	for y := 0; y < dh; y++ {
		sy := y * sh / dh
		for x := 0; x < dw; x++ {
			sx := x * sw / dw
			out[y*dw+x] = src[sy*sw+sx]
		}
	}
	return out
}

func packMono(src []byte, w int, h int) []byte {
	row := w / 8
	out := make([]byte, row*h)
	for y := 0; y < h; y++ {
		for bx := 0; bx < row; bx++ {
			var packed byte
			for bit := 0; bit < 8; bit++ {
				px := src[y*w+bx*8+bit]
				r := int((px >> 4) & 0x03)
				g := int((px >> 2) & 0x03)
				b := int(px & 0x03)
				luma := 3*r + 6*g + b
				if luma <= 5 {
					packed |= 1 << (7 - bit)
				}
			}
			out[y*row+bx] = packed
		}
	}
	return out
}

func EncodeMonochromeCover(raw []byte, width int, height int) (string, error) {
    watchimage.SetConfig(
        0.90,
        0.95,
        2,
        2,
        false,
        true,
        45000,
        1000,
        100,
    )
    return watchimage.Encode(raw, watchimage.Options{
        MaxW:      width,
        MaxH:      height,
        MaxBytes:  0,
        Palettize: true,
        AutoDepth: true,
        MaxDepth:  watchimage.Depth1,
        Coverage:  0.95,
    })
}

func ExtractPixelBytes(encoded string) ([]byte, error) {
    var result watchimage.WatchImageResult
    if err := json.Unmarshal([]byte(encoded), &result); err != nil {
        return nil, err
    }
    return base64.StdEncoding.DecodeString(result.Pixels)
}

func EncodeCoverMonoBytes(raw []byte, width int, height int) ([]byte, error) {
	watchimage.SetConfig(
		0.90,
		0.95,
		2,
		2,
		false,
		true,
		45000,
		1000,
		100,
	)
	encoded, err := watchimage.Encode(raw, watchimage.Options{
		MaxW:      width,
		MaxH:      height,
		MaxBytes:  0,
		Palettize: true,
		AutoDepth: true,
		MaxDepth:  watchimage.Depth1,
		Coverage:  0.95,
	})
	if err != nil {
		return nil, err
	}

	var result watchimage.WatchImageResult
	if err := json.Unmarshal([]byte(encoded), &result); err != nil {
		return nil, err
	}
	g8, sw, sh, err := decodeToGColor8(result)
	if err != nil {
		return nil, err
	}
	resized := resizeNearest(g8, sw, sh, width, height)
	return packMono(resized, width, height), nil
}

func EncodeCoverColorBytes(raw []byte, width int, height int) ([]byte, error) {
    // NOTE: SetConfig's thumbCapColors/fullCapColors/compressFit args below are dead
    // code for the Encode() API used here - they're only consulted by the separate
    // EncodeImageForWatch() wrapper, which we don't call. The color-count ceiling
    // that actually matters is Options.MaxDepth below. Only the dither* args here
    // are real (Encode() reads the package-level cfgDither* globals SetConfig writes).
    watchimage.SetConfig(
        0.90,
        0.95,
        4,
        64,
        false,
        true,
        45000,
        1000,
        100,
    )
    // Depth8 lets the quantizer use the watch's full 64-color gamut instead of
    // being capped at a 16-entry palette; the wire payload is already unpacked
    // to 1 byte/pixel regardless of depth (see decodeToGColor8), so this is a
    // pure quality win with no BLE transfer cost. Coverage raised slightly above
    // the library's own default (0.97) now that MaxDepth allows the full gamut,
    // so busier covers retain a bit more real color before falling back to
    // dithering for the rest.
    encoded, err := watchimage.Encode(raw, watchimage.Options{
        MaxW:      width,
        MaxH:      height,
        MaxBytes:  0,
        Palettize: true,
        AutoDepth: true,
        MaxDepth:  watchimage.Depth8,
        Coverage:  0.98,
    })
    if err != nil {
        return nil, err
    }

    var result watchimage.WatchImageResult
    if err := json.Unmarshal([]byte(encoded), &result); err != nil {
        return nil, err
    }
	g8, sw, sh, err := decodeToGColor8(result)
	if err != nil {
		return nil, err
	}
	return resizeNearest(g8, sw, sh, width, height), nil
}
