// Holds a status bit that software sets and reset clears through a library flop with set and reset pins.
// Library cell: body not supplied.
(* black_box *) module sky_dffsr (input logic CK, input logic D, input logic SN, input logic RN, output logic Q);
endmodule

module top (
  input  logic clk,
  input  logic rst_ni,
  input  logic clr_ni,
  input  logic set_ni,
  output logic status_o
);
  logic status_q;

  sky_dffsr u_st (.CK(clk), .D(1'b1), .SN(set_ni), .RN(rst_ni & clr_ni), .Q(status_q));

  assign status_o = status_q;
endmodule
