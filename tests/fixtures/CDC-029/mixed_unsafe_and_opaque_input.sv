// sverity-args: --top top --sdc @DIR@/data_input_async.sdc
// expect: CDC-029 ERROR
// expect-count: CDC-029 WARNING 1
module top(input logic src_clk, dst_clk, data, output logic q_bad, q_opaque);
  function automatic logic opaque(input logic x); return 1'b0; endfunction
  always_ff @(posedge dst_clk) begin
    q_bad <= data;
    q_opaque <= opaque(data);
  end
endmodule
