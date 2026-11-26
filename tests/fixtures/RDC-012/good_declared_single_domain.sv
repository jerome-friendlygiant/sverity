// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --config @DIR@/power_intent.json
// The configuration declares one always-on power domain: a stated trust model, no marker.
// expect: RDC-012 INFO
// expect-none: RDC-012 WARNING
module top(input logic clka, rst_n, d, output logic q);
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) q <= 1'b0; else q <= d;
endmodule
