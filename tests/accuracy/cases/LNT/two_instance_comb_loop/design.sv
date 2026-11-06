module phase_cell #(
  parameter bit INVERT = 1'b0
) (
  input  logic feedback,
  output logic phase
);
  generate
    if (INVERT) begin : gen_inverting
      assign phase = ~feedback;
    end else begin : gen_buffer
      assign phase = feedback;
    end
  endgenerate
endmodule

module loop_pair (
  output logic left_phase,
  output logic right_phase
);
  phase_cell #(.INVERT(1'b1)) u_left (
    .feedback(right_phase),
    .phase(left_phase)
  );

  phase_cell #(.INVERT(1'b1)) u_right (
    .feedback(left_phase),
    .phase(right_phase)
  );
endmodule

module top (
  output logic left_phase,
  output logic right_phase
);
  loop_pair u_pair (
    .left_phase(left_phase),
    .right_phase(right_phase)
  );
endmodule
