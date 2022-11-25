package main

import (
	"crypto"
	"flag"
	// "crypto/rand"
	// "crypto/rsa"
	"bufio"
	"crypto/x509"
	"encoding/pem"
	"fmt"
	"github.com/go-fed/httpsig"
	"io"
	"net/http"
	"os"
	"strings"
)

func getPrivateKey(r io.Reader) (crypto.PrivateKey, error) {
	bytes, err := io.ReadAll(r)
	if err != nil {
		return nil, err
	}

	pem, _ := pem.Decode(bytes)
	if pem == nil {
		return nil, fmt.Errorf("Invalid PEM")
	}

	if pem.Type == "RSA PRIVATE KEY" {
		key, err := x509.ParsePKCS1PrivateKey(pem.Bytes)

		if err != nil {
			return nil, err
		}
		return key, err
	}
	return nil, fmt.Errorf("Unknown key type")
}

func sign(privateKey crypto.PrivateKey, pubKeyId string, r *http.Request, b []byte, webfs bool) error {
	// Should use something more secure, but Mastodon only supports RSA_SHA256
	prefs := []httpsig.Algorithm{httpsig.RSA_SHA256, httpsig.RSA_SHA256}
	digestAlg := httpsig.DigestSha256
	headersToSign := []string{httpsig.RequestTarget, "date", "digest", "host", "content-type"}
	if b == nil {
		headersToSign = []string{httpsig.RequestTarget, "date", "host", "content-type"}
	}

	signer, chosenAlgo, err := httpsig.NewSigner(prefs, digestAlg, headersToSign, httpsig.Signature, 60*60*24*30)
	if err != nil {
		return err
	}
	// x := strings.NewReader("ABC")
	if err := signer.SignRequest(privateKey, pubKeyId, r, b); err != nil {

		return err
	}
	if !webfs {
		fmt.Printf("%v %v %v\n", r.Method, r.URL.Path, r.Proto)
	}
	for h := range r.Header {
		v := r.Header.Get(h)
		if h == "Signature" {
			v = strings.Replace(v, "hs2019", fmt.Sprintf("%s", chosenAlgo), 1)
		}

		if webfs {
            // webfs adds host back for us
            if h != "Host" {
                fmt.Printf("headers %v: %v\n", h, v)
            }
		} else {
			fmt.Printf("%v: %v\n", h, v)
		}
	}

	return nil
}

func parseFile(f io.Reader) (*http.Request, error) {
	reader := bufio.NewReader(f)
	r, err := http.ReadRequest(reader)
	return r, err
}

func usage() {
	fmt.Fprintf(os.Stderr, "usage: %v [-w] [-b bodyfile] [-p keyfile] keyname\n", os.Args[0])
	os.Exit(1)
}
func main() {
	if len(os.Args) < 3 {
		usage()
	}
	var privateKeyPEM, reqBody string
	var webfs bool
	flag.StringVar(&privateKeyPEM, "p", "", "Path to private key PEM file")
	flag.StringVar(&reqBody, "b", "", "File containing the body that will be posted for calculating the digest")
	flag.BoolVar(&webfs, "w", false, "Output headers in format suitable for printing to webfs ctl file")
	flag.Parse()

	args := flag.Args()
	if privateKeyPEM == "" || len(args) < 1 {
		usage()
	}
	req, err := parseFile(os.Stdin)
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(1)

	}
	// fmt.Printf("%v", req.Host)

	f, err := os.Open(privateKeyPEM)
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(1)
	}
	defer f.Close()

	privateKey, err := getPrivateKey(f)
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(1)
	}
	req.Header.Add("Host", req.Host)

	var bodyBytes []byte
	if reqBody != "" {
		bb, err := os.ReadFile(reqBody)
		if err != nil {
			fmt.Fprintf(os.Stderr, "err: %v\n", err)
			os.Exit(1)
		}
		bodyBytes = bb
	}
	if err := sign(privateKey, args[0], req, bodyBytes, webfs); err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(1)
	}

	os.Exit(0)

}
