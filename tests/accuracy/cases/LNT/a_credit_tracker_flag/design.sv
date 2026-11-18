// Credit counter with an optional underflow flag selected at build time.
module top #(
  parameter bit WITH_FLAG = 1'b1
) (
  input  logic       clk,
  input  logic       rst_n,
  input  logic       send,
  input  logic       ack,
  output logic       can_send,
  output logic [3:0] credits,
  output logic       underflow_o
);
  logic [3:0] cred_q;
  logic       do_send;
  logic       do_ack;

  assign can_send = (cred_q != 4'd0);
  assign do_send  = send && can_send;
  assign do_ack   = ack && (cred_q != 4'd15);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                  cred_q <= 4'd15;
    else if (do_send && !do_ack) cred_q <= cred_q - 4'd1;
    else if (do_ack && !do_send) cred_q <= cred_q + 4'd1;
  end

  if (WITH_FLAG) begin : g_flag
    assign underflow_o = send && !can_send;
  end else begin : g_noflag
    assign underflow_o = 1'b0;
  end

  assign credits = cred_q;
endmodule
