// expect: LNT-015 WARNING 6
// expect: LNT-063 WARNING 5
module top(input logic clk, rst, output logic [3:0] count);
  always @(posedge clk) begin
    if (rst) count <= 0;
    else count++;
  end
endmodule
