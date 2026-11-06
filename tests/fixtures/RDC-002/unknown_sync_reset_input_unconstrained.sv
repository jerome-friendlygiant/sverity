// sverity-args: --top top --sdc @DIR@/sync_reset_clock_only.sdc
// revert: if (clear) count <= '0; => if (count == 4'hf) count <= '0;
// revert-expect-none: RDC-002
// A top-level input clears the counter synchronously, but nothing declares it synchronous to clk.
// expect: RDC-002 WARNING 8
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, clear, enable, output logic [3:0] count);
  always_ff @(posedge clk) begin
    if (clear) count <= '0;
    else if (enable) count <= count + 1'b1;
  end
endmodule
