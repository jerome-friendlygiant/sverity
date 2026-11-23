// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-014 INFO
// expect-none: CDC-014 WARNING
// expect: RDC-023 WARNING
// expect-none: RDC-023 INFO
module top(input logic clka, clkb, rst_a_n, rst_b_n, pulse, output logic ev);
  logic tgl, s1, s2, s3;
  always_ff @(posedge clka or negedge rst_a_n) if (!rst_a_n) tgl <= 0; else if (pulse) tgl <= ~tgl;
  always_ff @(posedge clkb or negedge rst_b_n) if (!rst_b_n) {s3, s2, s1} <= '0; else {s3, s2, s1} <= {s2, s1, tgl};
  assign ev = s2 ^ s3;
endmodule
