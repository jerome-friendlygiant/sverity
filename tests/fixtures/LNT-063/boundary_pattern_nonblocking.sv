// expect-none: LNT-063
module top(input logic clk, input logic [3:0] d, output logic [1:0] q);
  typedef struct packed { logic [1:0] a, b; } pair_t;
  logic [1:0] value, unused_value;
  always @(posedge clk) begin
    pair_t'{value, unused_value} <= pair_t'(d);
    q <= value;
  end
endmodule
