// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-unresolved: CDC-014
// expect-none: CDC-014 INFO
module top(input logic clka, clkb, pulse, output logic lvl);
  logic tgl, s1, s2, s3, ev;
  always_ff @(posedge clka) if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb) {s3, s2, s1} <= {s2, s1, tgl};
  assign ev = s2 ^ s3;
  assign lvl = s2;
endmodule
