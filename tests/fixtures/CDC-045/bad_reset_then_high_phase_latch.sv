// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc
// expect: CDC-045 WARNING
// expect-none: CDC-045 INFO
module top(input logic clka,clkb,rst_n,go,test,en_d,d, output logic q);
logic en_q,en_l,g; always_ff @(posedge clka) en_q<=en_d;
always_latch if (!rst_n) en_l = 1'b0; else if (clka) en_l = en_q;
assign g = clka & en_l;
always_ff @(posedge g) q <= d;
endmodule
