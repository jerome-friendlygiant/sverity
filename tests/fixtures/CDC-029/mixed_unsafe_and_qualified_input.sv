// sverity-args: --top top --sdc @DIR@/input_async.sdc
// expect: CDC-029 ERROR
// expect: CDC-039 WARNING
module top(input logic src_clk, dst_clk, req, data, output logic q_bad, q_payload);
  logic r1, r2;
  always_ff @(posedge dst_clk) begin
    r1 <= req;
    r2 <= r1;
    q_bad <= data;
    if (r2) q_payload <= data;
  end
endmodule
