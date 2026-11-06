// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A reset built from two stages of one synchroniser is one reset request, reported once.
// expect: RDC-024 WARNING
// expect-count: RDC-024 WARNING 1
module top(input logic clka, clkb, rst_n, req, d, output logic q);
  logic r_a, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) r_a <= 1'b1; else r_a <= req;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) {s3, s2, s1} <= 3'b111; else {s3, s2, s1} <= {s2, s1, r_a};
  wire release_n = s2 & s3;
  always_ff @(posedge clkb or negedge release_n) if (!release_n) q <= 1'b0; else q <= d;
endmodule
