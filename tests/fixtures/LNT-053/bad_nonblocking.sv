// sverity-args: --top top
// the first nonblocking update is overwritten by the second in the same clocked block
// expect: LNT-053 WARNING 6
module top(input logic clk, a, b, output logic q);
  always_ff @(posedge clk) begin
    q <= a;
    q <= b;
  end
endmodule
