// sverity-args: --top top
// a concatenation target writes both operands with a blocking assignment in a clocked block
// expect: LNT-015 WARNING 6
module top(input logic clk, input logic [1:0] d, output logic a, b);
  always @(posedge clk) begin
    {a, b} = d;
  end
endmodule
