// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-044 WARNING
// expect-confidence: CDC-044 heuristic
module top(input logic clka, clkb, input logic [2047:0] d, input logic ev, output logic [2047:0] o, output logic [2047:0] o2, output logic p);
logic [2047:0] l, s1,s2,s3,t;
always_ff @(posedge clka) l <= d;
always_ff @(posedge clkb) begin s1<=l; s2<=s1; s3<=s2; end
assign t[0] = s2[0];
for (genvar i=1;i<2048;i++) begin: g assign t[i] = t[i-1] ^ s2[i]; end
assign o = t; assign o2 = s3;
logic e1,e2,e3; always_ff @(posedge clkb) begin e1<=ev; e2<=e1; e3<=e2; end
assign p = e2 & ~e3;
endmodule
