module selectable_hold #(
  parameter bit FAST = 1'b0
) (
  input  logic       load,
  input  logic [7:0] data,
  input  logic [7:0] previous,
  output logic [7:0] value
);
  generate
    if (FAST) begin : gen_fast
      always_comb begin
        if (load)
          value = data;
        else
          value = previous;
      end
    end else begin : gen_low_power
      always_comb begin
        if (load)
          value = data;
      end
    end
  endgenerate
endmodule

module top (
  input  logic       load,
  input  logic [7:0] data,
  input  logic [7:0] previous,
  output logic [7:0] value
);
  selectable_hold #(.FAST(1'b0)) u_selectable (
    .load(load),
    .data(data),
    .previous(previous),
    .value(value)
  );
endmodule
