// Data mux steered by the grant vector from the upstream arbiter.
// Interface:
//   req   one bit per source, lowest index serviced first
//   d0-d3 source data words
//   sel_data chosen word, zero when nothing is requested
//   any   at least one request present
module top (
  input  logic [3:0] req,
  input  logic [7:0] d0,
  input  logic [7:0] d1,
  input  logic [7:0] d2,
  input  logic [7:0] d3,
  output logic [7:0] sel_data,
  output logic       any
);
  always_comb begin
    unique case (1'b1)
      req[0]:  sel_data = d0;
      req[1]:  sel_data = d1;
      req[2]:  sel_data = d2;
      req[3]:  sel_data = d3;
      default: sel_data = 8'h00;
    endcase
  end

  assign any = |req;
endmodule
