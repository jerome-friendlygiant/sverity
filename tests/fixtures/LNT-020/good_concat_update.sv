// sverity-args: --top top
// expect-none: LNT-020
module top(input logic clk,rst_n,d, output logic q,r);
always @(posedge clk or negedge rst_n) if (!rst_n) begin q <= 0; r <= 0; end else {q,r} <= {d,~d};
endmodule
