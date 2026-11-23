// sverity-args: --top top --sdc @FIXTURES@/CDC-029/input_async.sdc
// Complementary mux mixing a launch-domain register and an asynchronous primary input disproves qualification.
// expect: CDC-028 ERROR
// expect-confidence: CDC-028 established
// expect-none: CDC-039
module top(input logic src_clk, dst_clk, req, input logic [3:0] data, input logic [3:0] d_reg_in, output logic [3:0] q);
  logic r1, r2;
  logic [3:0] src_flop;
  always_ff @(posedge src_clk) src_flop <= d_reg_in;
  always_ff @(posedge dst_clk) begin r1 <= req; r2 <= r1; end
  always_ff @(posedge dst_clk) begin
    q <= r2 ? src_flop : data;
  end
endmodule
