// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// A clock enable in the destination domain does not synchronise the data.
// expect: CDC-028 ERROR
module top(input logic clka, clkb, rst_n, en_b, d, output logic q);
  logic a_q;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) a_q <= 1'b0; else a_q <= d;
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 1'b0; else if (en_b) q <= a_q;
endmodule
