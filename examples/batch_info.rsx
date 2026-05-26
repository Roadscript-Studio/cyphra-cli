for file in glob("tests/fixtures/input/*.jpg") {
  info {
    in: file
    protocol: classic
  }
}
