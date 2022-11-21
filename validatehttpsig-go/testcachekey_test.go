package main

import (
	//"crypto/sha256"
	//"fmt"
	"os"
	"testing"
	"time"
)

func TestCacheKey(t *testing.T) {
	os.Setenv("fedi9_keydir", ".")
	// filepath := fmt.Sprintf("keys/%x.key", sha256.Sum256([]byte(keyId)))
	err := cacheKey("key1", "owner1", []byte{34, 34}, time.Unix(333, 0))
	if err != nil {
		t.Fail()
	}
	data, owner, err := getKeyFromCache("key1")
	if err != nil {
		t.Error(err)
	}
	if string(data) != string([]byte{34, 34}) {
		t.Error("Incorrect data retrieved")
	}
	if owner != "owner1" {
		t.Error("Bad owner")
	}

}
