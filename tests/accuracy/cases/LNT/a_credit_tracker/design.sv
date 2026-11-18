// Credit counter for a link: one credit consumed per send, one returned per ack.
module top #(
  parameter int MAX_CREDITS = 15
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
  assign do_ack   = ack && (cred_q != 4'(MAX_CREDITS));

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n)                      cred_q <= 4'(MAX_CREDITS);
    else if (do_send && !do_ack)     cred_q <= cred_q - 4'd1;
    else if (do_ack && !do_send)     cred_q <= cred_q + 4'd1;
  end

  assign credits = cred_q;
endmodule
