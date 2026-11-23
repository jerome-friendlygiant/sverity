// sverity-args: --top top
// expect-unresolved: CDC-044
// expect-coverage: graph-precision INCOMPLETE
module top(input logic clka, input logic [2047:0] rx, output logic [2047:0] o, output logic [2047:0] o2);
logic [2047:0] s1,s2,s3,t;
always_ff @(posedge clka) begin s1<=rx; s2<=s1; s3<=s2; end
assign t[0] = s2[0];
for (genvar i=1;i<2048;i++) begin: g assign t[i] = t[i-1] ^ s2[i]; end
assign o = t; assign o2 = s3;
endmodule
