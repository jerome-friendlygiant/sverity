// sverity-args: --top top --sdc @FIXTURES@/_common/async.sdc --profile fpga
// A qualifier that also depends on the delayed-copy stage prevents the current detector-boundary proof.
// Preserve the conservative mixed-stage diagnostic until this form can be distinguished safely.
// expect: CDC-012 WARNING
module sync2(input logic clk, rst_n, serial_i, output logic serial_o);
  (* ASYNC_REG = "TRUE" *) logic [1:0] reg_q;
  always_ff @(posedge clk or negedge rst_n)
    if (!rst_n) reg_q <= '0;
    else reg_q <= {reg_q[0], serial_i};
  assign serial_o = reg_q[1];
endmodule

module top(input logic clka, clkb, rst_n, d, output logic load);
  logic launch, synced, prior, ack, valid_o;
  always_ff @(posedge clka) launch <= d;
  sync2 i_sync(.clk(clkb), .rst_n, .serial_i(launch), .serial_o(synced));
  always_ff @(posedge clkb or negedge rst_n)
    if (!rst_n) begin prior <= 1'b0; ack <= 1'b0; end
    else begin prior <= synced; ack <= ~ack; end
  assign valid_o = (ack != prior);
  assign load = (synced != prior) && !valid_o;
endmodule
