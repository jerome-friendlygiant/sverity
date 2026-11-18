// sverity-args: --top top
// expect: LNT-054 WARNING 6
module top(input logic clk,rst_n,d, output logic y);
logic mode;
always @(posedge clk or negedge rst_n) if (!rst_n) mode <= 1'bx; else mode <= d;
assign y = mode ? d : 0;
endmodule
// revert: mode <= 1'bx => mode <= 1'b0
// revert-expect-none: LNT-054
