let input = "tests/fixtures/input/camera.jpg"
let output = "/tmp/roadscript_mosaic_example.png"
let debug_json = "/tmp/roadscript_mosaic_example.json"
let debug_svg = "/tmp/roadscript_mosaic_example.svg"

embed {
  in: input
  out: output
  msg_block: "hello mosaic"
  protocol: mosaic
}

verify {
  in: output
  protocol: mosaic
  layout: auto
  step_search: true
  debug_json: debug_json
  debug_svg: debug_svg
}
