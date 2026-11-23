// sverity-args: --top top --sdc @FIXTURES@/CDC-003/excl.sdc
// expect: CDC-009 INFO
// expect-none: CDC-009 WARNING
module top(input logic clka, clkb, input logic [1:0] d, output logic x, y);
  logic [1:0] a_q; logic x1, y1; always_ff @(posedge clka) a_q <= d;
  always_ff @(posedge clkb) begin x1 <= a_q[0]; x <= x1; y1 <= a_q[1]; y <= y1; end
endmodule
