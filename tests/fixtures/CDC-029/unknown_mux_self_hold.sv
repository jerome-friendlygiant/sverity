// sverity-args: --top top --sdc @DIR@/input_async.sdc
// The exact q feedback arm is a hold; the remaining direct data arm is guarded
// by the recognized synchronizer, while CDC-039 retains the protocol obligation.
// expect: CDC-029 INFO
// expect: CDC-039 WARNING
// expect-none: CDC-029 ERROR
module top(input logic src_clk, dst_clk, req, data, output logic q);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= req;
    r2 <= r1;
  end
  always_ff @(posedge dst_clk) q <= r2 ? data : q;
endmodule
