// sverity-args: --goal rdc --top top --sdc @DIR@/parse_rejected_override_sync_reset.sdc
// revert: parse_rejected_override_sync_reset.sdc => sync_reset_input_delay.sdc
// revert-expect-none: RDC-002
// A parse-rejected set_input_delay override must not let an earlier valid delay silence RDC-002.
// expect: RDC-002 WARNING 8
// expect-coverage: reset-extraction INCOMPLETE
module top(input logic clk, clear, enable, output logic [3:0] count);
  always_ff @(posedge clk) begin
    if (clear) count <= '0;
    else if (enable) count <= count + 1'b1;
  end
endmodule
