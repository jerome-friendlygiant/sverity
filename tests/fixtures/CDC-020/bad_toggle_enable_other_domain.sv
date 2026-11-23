// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-020 ERROR
// expect-confidence: CDC-020 established
module top(input logic clka,clkb,rst_n,go,test,en_d,d, output logic q);
logic t,g; always_ff @(posedge clkb) t <= ~t;
assign g = clka & t;
always_ff @(posedge g) q <= d;
endmodule
