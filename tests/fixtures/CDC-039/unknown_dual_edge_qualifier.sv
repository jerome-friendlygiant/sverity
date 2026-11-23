// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect-unresolved: CDC-039
// expect-none: CDC-028 ERROR
// expect-coverage: clock-relationships INCOMPLETE
module top(input logic clka, clkb, rst_n, v, input logic [7:0] data, output logic [7:0] q);
  logic vld; logic [7:0] pay; logic s1, s2;
  always_ff @(posedge clka or negedge rst_n) if (!rst_n) begin vld <= 0; pay <= 0; end else begin vld <= v; if (v) pay <= data; end
  always @(edge clkb) begin s1 <= vld; s2 <= s1; end
  always_ff @(posedge clkb or negedge rst_n) if (!rst_n) q <= 0; else if (s2) q <= pay;
endmodule
