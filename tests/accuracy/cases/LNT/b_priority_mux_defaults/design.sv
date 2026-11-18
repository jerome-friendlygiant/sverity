// Picks the highest-numbered valid source among three.
module top (
  input  logic [7:0] s0,
  input  logic [7:0] s1,
  input  logic [7:0] s2,
  input  logic [2:0] valid,
  output logic [7:0] data,
  output logic [1:0] src,
  output logic       any_valid
);
  always_comb begin
    data      = 8'h00;
    src       = 2'd0;
    any_valid = 1'b0;
    if (valid[2]) begin
      data      = s2;
      src       = 2'd2;
      any_valid = 1'b1;
    end else if (valid[1]) begin
      data      = s1;
      src       = 2'd1;
      any_valid = 1'b1;
    end else if (valid[0]) begin
      data      = s0;
      any_valid = 1'b1;
    end
  end
endmodule
