// sverity-args: --top top --sdc @DIR@/explicit_async_mixed.sdc
// An effective asynchronous max contract makes the direct and partial captures established unsafe samples.
// expect: CDC-029 ERROR
// expect-confidence: CDC-029 established
module capture(input logic dst_clk, rst_n, req, output logic direct, sync_first, sync_second);
  always_ff @(posedge dst_clk or negedge rst_n)
    if (!rst_n) begin direct <= 1'b0; sync_first <= 1'b0; sync_second <= 1'b0; end
    else begin direct <= req; sync_first <= req; sync_second <= sync_first; end
endmodule

module top(input logic src_clk, dst_clk, rst_n, req, output logic direct, sync_first, sync_second);
  capture u_capture(.*);
endmodule
