// sverity-args: --top top
// expect: LNT-022 WARNING 5
// expect-coverage: rule-execution INCOMPLETE
module top(input logic [1:0] controls, input logic i,j,d, output logic q);
always @(posedge controls[i] or negedge controls[j]) q <= d;
endmodule
