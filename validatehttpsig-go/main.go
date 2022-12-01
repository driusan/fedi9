package main

import (
	"bufio"
	"crypto"
	"crypto/sha256"
	"crypto/x509"
	"encoding/json"
	"encoding/pem"
	"fmt"
	"github.com/go-fed/httpsig"
	"github.com/mischief/ndb"
	"net/http"
	"os"
	"strings"
	"time"
)

// An ActivityPub actor object. Only the parts
// we care about.
type ActorObject struct {
	PublicKey struct {
		Id           string `json:"id"`
		Owner        string `json:"owner"`
		PublicKeyPem string `json:"publicKeyPem"`
	} `json:"publicKey"`
}

func getKeyDir() (string, error) {
	ndbDir := os.Getenv("fedi9_keydir")
	if ndbDir == "" {
		ndbdir, err := os.UserHomeDir()
		if err != nil {
			return "", err
		}

        dir := fmt.Sprintf("%s/lib/fedi9/keys", ndbdir)
        if err := os.MkdirAll(dir, 0775); err != nil {
            fmt.Fprintf(os.Stderr, "%v", err)
            return "", err
        }
		return dir, nil
	}
	return ndbDir, nil
}
func cacheKey(keyId string, owner string, pembytes []byte, time time.Time) error {
	ndbDir, err := getKeyDir()
	if err != nil {
		return err
	}
	f, err := os.OpenFile(fmt.Sprintf("%s/knownkeys.db", ndbDir), os.O_APPEND|os.O_CREATE|os.O_WRONLY, 0644)
	if err != nil {
        fmt.Fprintf(os.Stderr, "%v", err)
		return err
	}
	defer f.Close()
	filepath := fmt.Sprintf("keys/%x.key", sha256.Sum256([]byte(keyId)))
	if err := os.MkdirAll(fmt.Sprintf("%s/keys", ndbDir), 0775); err != nil {
		return err
	}
	cacheFile, err := os.Create(fmt.Sprintf("%s/%s", ndbDir, filepath))
	if err != nil {
		return err
	}
	defer cacheFile.Close()
	if err := os.WriteFile(fmt.Sprintf("%s/%s", ndbDir, filepath), pembytes, 0644); err != nil {
		return err
	}

	fmt.Fprintf(f, "keyid=%v file=%v owner=%v retrieved=%v\n\n", keyId, filepath, owner, time.Unix())
	return nil
}

func getKeyFromCache(keyId string) (data []byte, owner string, err error) {
	ndbDir, err := getKeyDir()
	if err != nil {
		return nil, "", err
	}
	ndb, err := ndb.Open(fmt.Sprintf("%s/knownkeys.db", ndbDir))
	if err != nil {
		return nil, "", err
	}
	records := ndb.Search("keyid", keyId)
	if len(records) == 0 {
		return nil, "", fmt.Errorf("No records found")
	}
	err = nil
	for _, record := range records {
		for _, tuple := range record {
			switch tuple.Attr {
			case "owner":
				owner = tuple.Val
			case "file":
				filedata, err := os.ReadFile(fmt.Sprintf("%s/%s", ndbDir, tuple.Val))
				if err != nil {
					return nil, "", err
				}
				data = filedata
			default:
			}
		}
		if owner != "" && data != nil {
			return
		}
	}
	return nil, "", fmt.Errorf("Could not load data")
}

func parsePemKey(keyId, owner string, pemKey []byte, cache bool) (crypto.PublicKey, error) {
	pemBlock, _ := pem.Decode(pemKey)
	switch pemBlock.Type {
	case "PUBLIC KEY":
		key, err := x509.ParsePKIXPublicKey(pemBlock.Bytes)
		if err != nil {
			return nil, err
		}
		if cache {
			cacheKey(keyId, owner, pemKey, time.Now())
		}
		return key, nil
	case "RSA PUBLIC KEY":
		key, err := x509.ParsePKCS1PublicKey(pemBlock.Bytes)
		if err != nil {
			return nil, err
		}
		if cache {
			cacheKey(keyId, owner, pemKey, time.Now())
		}
		return key, nil
	default:
		return nil, fmt.Errorf("Could not key encoding from PEM")
	}
}

// FIXME: Cache the keyId somewhere instead of retrieving it constantly.
// Use github.com/mischief/ndb?
func getKey(keyId string) (crypto.PublicKey, error) {
	if key, owner, err := getKeyFromCache(keyId); err == nil {
		return parsePemKey(keyId, owner, key, false)
	}

	client := &http.Client{}
	req, err := http.NewRequest("GET", keyId, nil)
	if err != nil {
		return nil, err
	}

	req.Header.Add("Accept", `application/ld+json; profile="https://www.w3.org/ns/activitystreams"`)
	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	decoder := json.NewDecoder(resp.Body)
	var actor ActorObject
	if err := decoder.Decode(&actor); err != nil {
		return nil, err
	}
	if actor.PublicKey.Id != keyId {
		return nil, fmt.Errorf("Could not retrieve %v, got %v", keyId, actor.PublicKey.Id)
	}
	return parsePemKey(keyId, actor.PublicKey.Owner, []byte(actor.PublicKey.PublicKeyPem), true)
}

// httpsig doesn't like the algorithm header, but we do.
func getAlgorithm(header string) (httpsig.Algorithm, error) {
	pieces := strings.Split(header, ",")
	for _, s := range pieces {
		if !strings.HasPrefix(s, "algorithm=") {
			continue
		}
		val := strings.Replace(s, "algorithm=", "", 1)
		val = val[1 : len(val)-1]
        if val == "hs2019" {
            fmt.Printf("Warning: hs2019\n")
            return "rsa-sha256", nil
            // return "", fmt.Errorf("hs2019 algorithm not supported")
        }
		/*
		   this doesn't seem to be working so just assume it's
		   supported for now

		   if httpsig.IsSupportedHttpSigAlgorithm(val) {
		       fmt.Printf("'%s'\n", val);
		   } else {

		       fmt.Printf("'%s' unsupported\n", val);
		   }
		*/
		return httpsig.Algorithm(val), nil
	}
	return "", fmt.Errorf("Could not determine algorithm")
}
func main() {
	if len(os.Args) < 2 {
		fmt.Fprintf(os.Stderr, "usage: %v filename\n", os.Args[0])
		os.Exit(1)
	}
	f, err := os.Open(os.Args[1])
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(2)
	}
	defer f.Close()

	reader := bufio.NewReader(f)
	req, err := http.ReadRequest(reader)
	if err != nil {
		fmt.Fprintf(os.Stderr, "err1: %v\n", err)
		os.Exit(3)

	}
	verifier, err := httpsig.NewVerifier(req)
	if err != nil {
		fmt.Fprintf(os.Stderr, "err2: %v\n", err)
		os.Exit(4)
	}
	algorithm, err := getAlgorithm(req.Header.Get("Signature"))
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(5)
	}
	pubkey, err := getKey(verifier.KeyId())
	if err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(6)
	}
	if err := verifier.Verify(pubkey, algorithm); err != nil {
		fmt.Fprintf(os.Stderr, "err: %v\n", err)
		os.Exit(7)
	}
	os.Exit(0)

}
