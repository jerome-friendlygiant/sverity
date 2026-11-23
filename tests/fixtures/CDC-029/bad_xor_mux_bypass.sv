// sverity-args: --top top --sdc @DIR@/input_async.sdc
// Both ternary arms sample data; the false arm is not a self-hold.
// expect: CDC-029 ERROR
// expect-none: CDC-039
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= req;
    r2 <= r1;
    q <= r2 ? data : (q ^ data);
  end
endmodule
