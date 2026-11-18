// sverity-args: --top top
// expect-none: LNT-022
module top(input logic [1:0] controls, input logic d, output logic q);
always @(posedge controls[0] or negedge controls[1]) if (!controls[1]) q <= 0; else q <= d;
endmodule
