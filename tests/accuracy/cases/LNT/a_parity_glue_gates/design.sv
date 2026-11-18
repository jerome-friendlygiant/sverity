// Gate-level parity tree for four request lines, registered once.
// par_o is the XOR of all four lines one cycle after they change; the
// tree is written with primitives so it maps to a fixed XOR structure.
module top (
  input  logic       clk,
  input  logic       rst_n,
  input  logic [3:0] d,
  output logic       par_o,
  output logic       any_o
);
  wire p01;
  wire par_w;
  logic par_q;

  xor g0 (p01, d[0], d[1]);
  xor g1 (p23, d[2], d[3]);
  xor g2 (par_w, p01, p23);

  always_ff @(posedge clk or negedge rst_n) begin
    if (!rst_n) par_q <= 1'b0;
    else        par_q <= par_w;
  end

  assign par_o = par_q;
  assign any_o = |d;
endmodule
