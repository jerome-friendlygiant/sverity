// sverity-args: --top top --sdc @DIR@/input_async.sdc
// The synchronized request gates the expression, but the asynchronous payload
// itself is sampled as a control and therefore is not a qualified data transfer.
// expect: CDC-029 ERROR
// expect-none: CDC-039
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= req;
    r2 <= r1;
    if (r2 && data) q <= 1'b1;
  end
endmodule
