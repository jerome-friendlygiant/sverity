// sverity-args: --top top --sdc @DIR@/sync_reset_input_delay.sdc
// The input delay declares the synchronous reset synchronous to clk.
// expect-none: RDC-002
module top(input logic clk, clear, enable, output logic [3:0] count);
  always_ff @(posedge clk) begin
    if (clear) count <= '0;
    else if (enable) count <= count + 1'b1;
  end
endmodule
