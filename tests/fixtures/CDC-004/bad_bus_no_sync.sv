// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// revert: always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q => always_ff @(posedge clka or negedge rst_n) if (!rst_n) b_q
// revert-expect-none: CDC-004 ERROR
// expect: CDC-004 ERROR
module top(input logic clka, clkb, rst_n, input logic [3:0] d, output logic [3:0] q);
  logic [3:0] a_q, b_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= '0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) b_q <= '0; else b_q <= a_q;
  assign q = b_q;
endmodule
