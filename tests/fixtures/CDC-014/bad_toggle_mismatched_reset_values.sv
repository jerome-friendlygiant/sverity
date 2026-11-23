// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 WARNING
// expect-unresolved: CDC-014
// expect-none: CDC-014 INFO
// expect: RDC-023 WARNING
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, rst_n, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) tgl <= 0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s1 <= 0; else s1 <= tgl;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s2 <= 1; else s2 <= s1;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) s3 <= 0; else s3 <= s2;
  assign ev = s2 ^ s3;
endmodule
