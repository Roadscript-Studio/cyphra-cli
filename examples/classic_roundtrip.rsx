let input = "tests/fixtures/input/input.jpg"
let output = "roadscript_classic_example.png"

embed {
  in: input
  out: output
  msg_block: "hello classic"
  protocol: classic
}

verify {
  in: output
  protocol: classic
}
