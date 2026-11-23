// C05
module c05(input logic clk_s, clk_d, input logic rst_n, input logic send, input logic [7:0] payload_s, output logic [7:0] payload_d, output logic valid_d, input logic accept_d);
  logic req_s, ack_d;
  logic [7:0] hold_s;
  (* ASYNC_REG = "TRUE" *) logic req_d1, req_d2;
  (* ASYNC_REG = "TRUE" *) logic ack_s1, ack_s2;
  always_ff @(posedge clk_s or negedge rst_n) if (!rst_n) begin req_s<=0; hold_s<=0; ack_s1<=0; ack_s2<=0; end else begin ack_s1<=ack_d; ack_s2<=ack_s1; if (send && (req_s == ack_s2)) begin hold_s<=payload_s; req_s<=~req_s; end end
  always_ff @(posedge clk_d or negedge rst_n) if (!rst_n) begin req_d1<=0; req_d2<=0; ack_d<=0; payload_d<=0; valid_d<=0; end else begin req_d1<=req_s; req_d2<=req_d1; valid_d<=0; if (req_d2 != ack_d) begin payload_d<=hold_s; valid_d<=1; end if (accept_d && valid_d) ack_d<=req_d2; end
endmodule
