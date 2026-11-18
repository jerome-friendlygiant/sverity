// sverity-args: --top top
// expect: LNT-054 WARNING 6
module top(input logic clk, rst_n, d, output logic y);
logic [1:0] state;
always @(posedge clk or negedge rst_n) if (!rst_n) state[0] <= 0; else state <= {d,d};
assign y = state[1] ? d : 0;
endmodule
