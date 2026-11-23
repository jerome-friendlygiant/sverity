// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --option CDC-005.min_stages=3
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
// expect: CDC-024 WARNING
module top(input logic clka, clkb, pulse, output logic ev);
  logic tgl, s1, s2, s3, s4;
  always_ff @(posedge clka) if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb) {s4, s3, s2, s1} <= {s3, s2, s1, tgl};
  assign ev = s3 != s4;
endmodule
