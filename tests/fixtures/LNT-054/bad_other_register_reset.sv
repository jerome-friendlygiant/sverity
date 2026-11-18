// sverity-args: --top top
// expect: LNT-054 WARNING 9
module top(input logic clk, rst_n, d, output logic y);
logic state;
logic other;
always @(posedge clk or negedge rst_n)
 if (!rst_n) other <= 0;
 else begin other <= d; state <= d; end
assign y = state ? other : d;
endmodule
