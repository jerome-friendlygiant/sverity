// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A 140000-bit packed bus exceeds the per-bit tracking limit for packed values (131072 bits): it must NOT be reported as a
// recognised synchroniser.
// expect: CDC-004 WARNING
// expect-none: CDC-003 INFO
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, clkb, rst_n, input logic [139999:0] d, output logic [139999:0] q);
  logic [139999:0] a, s1, s2;
  always_ff @(posedge clka or negedge rst_n) if(!rst_n) a <= '0; else a <= d;
  always_ff @(posedge clkb or negedge rst_n) if(!rst_n) {s2,s1} <= '0; else {s2,s1} <= {s1,a};
  assign q = s2;
endmodule
