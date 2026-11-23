// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-unresolved: CDC-014
// expect-none: CDC-014 INFO
// expect: RDC-023 WARNING
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, srst, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka) if (srst) tgl <= 0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb) if (srst) {s3, s2, s1} <= 3'b010; else {s3, s2, s1} <= {s2, s1, tgl};
  assign ev = s2 ^ s3;
endmodule
