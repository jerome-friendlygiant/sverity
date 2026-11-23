// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-020 ERROR
// expect: CDC-045 WARNING
// expect-none: CDC-019
module top(input logic clka,clkb,rst_n,go,test,en_d,d, output logic q);
logic t,en,g; always_ff @(posedge clkb) t<=~t;
assign en = go ? t : 1'b0;
assign g = clka & en;
always_ff @(posedge g) q <= d;
endmodule
