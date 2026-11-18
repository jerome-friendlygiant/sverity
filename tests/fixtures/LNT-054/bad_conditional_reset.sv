// sverity-args: --top top
// expect: LNT-054 WARNING 6
module top(input logic clk,rst_n,en,d, output logic y);
logic mode;
always @(posedge clk or negedge rst_n) if (!rst_n) begin if (en) mode <= 0; end else mode <= d;
assign y = mode ? d : 0;
endmodule
// revert: if (en) mode <= 0; => mode <= 0;
// revert-expect-none: LNT-054
