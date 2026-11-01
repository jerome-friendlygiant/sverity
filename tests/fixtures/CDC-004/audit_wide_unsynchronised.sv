// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-004 ERROR
// Regression from the audit: an unsynchronised wide crossing between declared-asynchronous clocks is an established ERROR.
// 2048-bit bus crossing declared-asynchronous clocks with no synchroniser at all.
module top(input logic clka, clkb, input logic [2047:0] d, output logic [2047:0] q);
  logic [2047:0] a_q;
  always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) q <= a_q;
endmodule
