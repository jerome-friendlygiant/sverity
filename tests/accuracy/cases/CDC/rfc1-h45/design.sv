// Wrapper around an externally supplied macro.
(* black_box *)
module vendor_ip (
  input  logic       clk_io,
  input  logic [7:0] cmd_i,
  output logic [7:0] resp_o
);
endmodule

module top (
  input  logic       clk_io,
  input  logic       rst_ni,
  input  logic [7:0] cmd_i,
  output logic [7:0] resp_o
);
  logic [7:0] cmd_q;
  logic [7:0] ip_resp;
  logic [7:0] resp_q;

  always_ff @(posedge clk_io) cmd_q <= cmd_i;

  vendor_ip u_ip (.clk_io(clk_io), .cmd_i(cmd_q), .resp_o(ip_resp));

  always_ff @(posedge clk_io) begin
    if (!rst_ni) resp_q <= 8'h00;
    else         resp_q <= ip_resp;
  end

  assign resp_o = resp_q;
endmodule
